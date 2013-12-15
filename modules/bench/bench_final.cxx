#include "bench_config.hpp"

#include <fstream>

#include <config.hpp>
#include <utils/filesystem.hpp>
#include <utils/numerics.hpp>
#include <utils/dataset.hpp>
#include <utils/misc.hpp>
#include <utils/vision.hpp>
#include <utils/logger.hpp>
#include <utils/image.hpp>
#include <utils/cycletimer.hpp>
#include <search/bag_of_words/bag_of_words.hpp>
#include <search/inverted_index/inverted_index.hpp>
#include <vis/matches_page.hpp>

#if ENABLE_MULTITHREADING && ENABLE_OPENMP
#include <omp.h>
#endif
#if ENABLE_MULTITHREADING && ENABLE_MPI
#include <mpi.h>
#endif

_INITIALIZE_EASYLOGGINGPP


void validate_results(Dataset &dataset, PTR_LIB::shared_ptr<const SimpleDataset::SimpleImage> &query_image, PTR_LIB::shared_ptr<InvertedIndex::MatchResults> &matches,
		MatchesPage &html_output) {
#if ENABLE_MULTITHREADING && ENABLE_MPI
    if (rank != 0)
      return;
#endif
	cv::Mat keypoints_0, descriptors_0;
	const std::string &query_keypoints_location = dataset.location(query_image->feature_path("keypoints"));
	const std::string &query_descriptors_location = dataset.location(query_image->feature_path("descriptors"));
	filesystem::load_cvmat(query_keypoints_location, keypoints_0);
	filesystem::load_cvmat(query_descriptors_location, descriptors_0);
	uint32_t num_validate = 16;
	std::vector<int> validated(num_validate, 0);  
#if ENABLE_MULTITHREADING && ENABLE_OPENMP
	#pragma omp parallel for schedule(dynamic)
#endif
	for(int32_t j=0; j<(int32_t)num_validate; j++) {
		cv::Mat keypoints_1, descriptors_1;
		PTR_LIB::shared_ptr<SimpleDataset::SimpleImage> match_image = std::static_pointer_cast<SimpleDataset::SimpleImage>(dataset.image(matches->matches[j]));
		const std::string &match_keypoints_location = dataset.location(match_image->feature_path("keypoints"));
		const std::string &match_descriptors_location = dataset.location(match_image->feature_path("descriptors"));
		filesystem::load_cvmat(match_keypoints_location, keypoints_1);
		filesystem::load_cvmat(match_descriptors_location, descriptors_1);

		cv::detail::MatchesInfo match_info;
		vision::geo_verify_f(descriptors_0, keypoints_0, descriptors_1, keypoints_1, match_info);

		validated[j] = vision::is_good_match(match_info) ? 1 : -1;
	}

	html_output.add_match(query_image->id, matches->matches, dataset, PTR_LIB::make_shared< std::vector<int> >(validated));
	html_output.write(dataset.location() + "/results/matches/");
}

void bench_dataset(SearchBase &searcher, Dataset &dataset) {
	uint32_t num_searches = 256;


#if ENABLE_MULTITHREADING && ENABLE_MPI
  int rank;
  MPI_Comm_rank(MPI_COMM_WORLD, &rank);
#endif

	MatchesPage html_output;
	

	const std::vector<PTR_LIB::shared_ptr<const Image> > &rand_images = dataset.random_images(num_searches);
	for(uint32_t i=0; i<num_searches; i++) {
		
		PTR_LIB::shared_ptr<const SimpleDataset::SimpleImage> query_image =  std::static_pointer_cast<const SimpleDataset::SimpleImage>(rand_images[i]);

		PTR_LIB::shared_ptr<InvertedIndex::MatchResults> matches = 
			std::static_pointer_cast<InvertedIndex::MatchResults>(searcher.search(dataset, 0, query_image));

		if(matches == 0) {
			LERROR << "Error while running search.";
			continue;
   		 }
		// validate matches
   		 validate_results(dataset, query_image, matches, html_output);
	}

}

int main(int argc, char *argv[]) {
#if ENABLE_MULTITHREADING && ENABLE_MPI
  MPI_Init(&argc, &argv);
#endif
		
	SimpleDataset oxford_train_dataset(s_oxfordmini_data_dir, s_oxfordmini_database_location, 0);
	const uint32_t num_clusters = s_oxfordmini_num_clusters;
	index_output_file << oxford_dataset.location() << "/index/" << num_clusters << ".index";
	InvertedIndex ii(index_output_file.str());

	size_t cache_sizes[] = {128, 256};
	for(int i=0; i<2; i++) {
		size_t cache_size = cache_sizes[i];

		SimpleDataset oxford_dataset(s_oxfordmini_data_dir, s_oxfordmini_database_location, cache_size);
		LINFO << oxford_dataset;

		std::stringstream index_output_file;

		bench_dataset(ii, oxford_dataset);
	}


#if ENABLE_MULTITHREADING && ENABLE_MPI
	MPI_Finalize();
#endif
	return 0;
}